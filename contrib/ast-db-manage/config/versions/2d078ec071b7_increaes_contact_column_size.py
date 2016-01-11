"""increaes_contact_column_size

Revision ID: 2d078ec071b7
Revises: 189a235b3fd7
Create Date: 2015-12-16 11:26:54.218985

"""

# revision identifiers, used by Alembic.
revision = '2d078ec071b7'
down_revision = '189a235b3fd7'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.alter_column('ps_aors', 'contact', type_=sa.String(255))


def downgrade():
    op.alter_column('ps_aors', 'contact', type_=sa.String(40))
