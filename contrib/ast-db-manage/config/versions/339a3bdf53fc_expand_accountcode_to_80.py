"""expand accountcode to 80

Revision ID: 339a3bdf53fc
Revises: 28ce1e718f05
Create Date: 2015-11-05 10:10:27.465794

"""

# revision identifiers, used by Alembic.
revision = '339a3bdf53fc'
down_revision = '28ce1e718f05'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.alter_column('ps_endpoints', 'accountcode', type_=sa.String(80))
    op.alter_column('sippeers', 'accountcode', type_=sa.String(80))
    op.alter_column('iaxfriends', 'accountcode', type_=sa.String(80))
    pass


def downgrade():
    op.alter_column('ps_endpoints', 'accountcode', type_=sa.String(20))
    op.alter_column('sippeers', 'accountcode', type_=sa.String(40))
    op.alter_column('iaxfriends', 'accountcode', type_=sa.String(20))
    pass
