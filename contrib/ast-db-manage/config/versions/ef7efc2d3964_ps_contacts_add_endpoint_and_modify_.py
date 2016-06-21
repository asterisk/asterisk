"""ps_contacts add endpoint and modify expiration_time to bigint

Revision ID: ef7efc2d3964
Revises: a845e4d8ade8
Create Date: 2016-06-02 18:18:46.231920

"""

# revision identifiers, used by Alembic.
revision = 'ef7efc2d3964'
down_revision = 'a845e4d8ade8'

from alembic import op
import sqlalchemy as sa


def upgrade():
    context = op.get_context()

    op.add_column('ps_contacts', sa.Column('endpoint', sa.String(40)))

    if context.bind.dialect.name != 'postgresql':
	op.alter_column('ps_contacts', 'expiration_time', type_=sa.BigInteger)
    else:
	op.execute('ALTER TABLE ps_contacts ALTER COLUMN expiration_time TYPE BIGINT USING expiration_time::bigint')

    op.create_index('ps_contacts_qualifyfreq_exptime', 'ps_contacts', ['qualify_frequency', 'expiration_time'])
    op.create_index('ps_aors_qualifyfreq_contact', 'ps_aors', ['qualify_frequency', 'contact'])
def downgrade():
    op.drop_index('ps_aors_qualifyfreq_contact')
    op.drop_index('ps_contacts_qualifyfreq_exptime')
    op.drop_column('ps_contacts', 'endpoint')
    op.alter_column('ps_contacts', 'expiration_time', type_=String(40))
