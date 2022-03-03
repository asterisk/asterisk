"""increase reg server size

Revision ID: 1ae0609b6646
Revises: 61797b9fced6
Create Date: 2020-08-31 13:50:19.772439

"""

# revision identifiers, used by Alembic.
revision = '1ae0609b6646'
down_revision = '61797b9fced6'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.alter_column('ps_contacts', 'reg_server', type_=sa.String(255))


def downgrade():
    op.alter_column('ps_contacts', 'reg_server', type_=sa.String(20))
